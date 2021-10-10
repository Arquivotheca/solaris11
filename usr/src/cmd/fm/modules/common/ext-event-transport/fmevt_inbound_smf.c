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

#include <strings.h>
#include <libscf.h>
#include <fm/fmd_api.h>
#include <fm/libtopo.h>
#include <fm/libfmevent.h>

#include "fmevt.h"

/*
 * Post-processing according to the FMEV_RULESET_SMF ruleset.
 *
 * Raw event we expect:
 *
 * ==========================================================================
 * Class: "state-transition"
 * Subclasses: The new state, one of SCF_STATE_STRING_* from libscf.h
 * Attr:
 * Name		DATA_TYPE_*	Description
 * ------------ --------------- ---------------------------------------------
 * fmri		STRING		svc:/... (svc scheme shorthand version)
 * transition	INT32		(old_state << 16) | new_state
 * reason-version UINT32	reason-short namespace version
 * reason-short	STRING		Short/keyword reason for transition
 * reason-long	STRING		Long-winded reason for the transition
 * ==========================================================================
 *
 * Protocol event components we return:
 *
 * ==========================================================================
 * Class: ireport.os.smf.state-transition.<new-state>
 * Attr:
 * Name		DATA_TYPE_*	Description
 * ------------ --------------- ----------------------------------------
 * svc		NVLIST		"svc" scheme FMRI of affected service instance
 * svc-string	STRING		SMF FMRI in short string form svc:/foo/bar
 * from-state	STRING		Previous state; SCF_STATE_STRING_*
 * to-state	STRING		New state; SCF_STATE_STRING_*
 * reason-version UINT32	reason-short namespace version
 * reason-short	STRING		Short/keyword reason for transition
 * reason-long	STRING		Long-winded reason for the transition
 * [linked-event] STRING	For from-state of "maintenance", references
 *				original maintenance event for this instance;
 *				absent if for some reason we do not have a
 *				reference available.
 * ==========================================================================
 */

static struct {
	fmd_stat_t smf_pp_cache_nents;
	fmd_stat_t smf_pp_cache_adds;
	fmd_stat_t smf_pp_cache_drop;
	fmd_stat_t smf_pp_cache_hit;
	fmd_stat_t smf_pp_cache_miss;
	fmd_stat_t smf_pp_cache_miss_empty;
	fmd_stat_t smf_pp_cache_badver;
	fmd_stat_t smf_pp_cache_badsz;
} fmevt_pp_smf_stats = {
	{ "smf_pp_cache_nents", FMD_TYPE_UINT64,
	    "number of entries in smf uuid cache" },
	{ "smf_pp_cache_adds", FMD_TYPE_UINT64,
	    "successful cache additions" },
	{ "smf_pp_cache_drop", FMD_TYPE_UINT64,
	    "entries dropped for full cache at max size" },
	{ "smf_pp_cache_hit", FMD_TYPE_UINT64,
	    "successful lookup in cache" },
	{ "smf_pp_cache_miss", FMD_TYPE_UINT64,
	    "lookup missed in cache" },
	{ "smf_pp_cache_miss_empty", FMD_TYPE_UINT64,
	    "lookup missed - cache was empty" },
	{ "smf_pp_cache_badver", FMD_TYPE_UINT64,
	    "cache checkpoint had bad version info" },
	{ "smf_pp_cache_badsz", FMD_TYPE_UINT64,
	    "cache checkpoint failed size verification" },
};

#define	SETSTAT(stat, val)	fmevt_pp_smf_stats.stat.fmds_value.ui64 = (val)
#define	BUMPSTAT(stat)		fmevt_pp_smf_stats.stat.fmds_value.ui64++

/*
 * svc.startd generates events using the FMRI shorthand (svc:/foo/bar)
 * instead of the standard form (svc:///foo/bar).  This function converts to
 * the standard representation.  The caller must free the allocated string.
 */
static char *
shortfmri_to_fmristr(const char *shortfmristr)
{
	size_t len;
	char *fmristr;

	if (strncmp(shortfmristr, "svc:/", 5) != 0)
		return (NULL);

	len = strlen(shortfmristr) + 3;
	fmristr = fmd_hdl_alloc(fmevt_hdl, len, FMD_SLEEP);
	(void) snprintf(fmristr, len, "svc:///%s", shortfmristr + 5);

	return (fmristr);
}

/*
 * Convert a shorthand svc FMRI into a full svc FMRI nvlist
 */
static nvlist_t *
shortfmri_to_fmri(const char *shortfmristr)
{
	nvlist_t *ret, *fmri;
	topo_hdl_t *thp;
	char *fmristr;
	int err;

	if ((fmristr = shortfmri_to_fmristr(shortfmristr)) == NULL)
		return (NULL);

	thp = fmd_hdl_topo_hold(fmevt_hdl, TOPO_VERSION);

	if (topo_fmri_str2nvl(thp, fmristr, &fmri, &err) != 0) {
		fmd_hdl_error(fmevt_hdl, "failed to convert '%s' to nvlist\n",
		    fmristr);
		fmd_hdl_strfree(fmevt_hdl, fmristr);
		fmd_hdl_topo_rele(fmevt_hdl, thp);
		return (NULL);
	}

	fmd_hdl_strfree(fmevt_hdl, fmristr);

	if ((ret = fmd_nvl_dup(fmevt_hdl, fmri, FMD_SLEEP)) == NULL) {
		fmd_hdl_error(fmevt_hdl, "failed to dup fmri\n");
		nvlist_free(fmri);
		fmd_hdl_topo_rele(fmevt_hdl, thp);
		return (NULL);
	}

	nvlist_free(fmri);
	fmd_hdl_topo_rele(fmevt_hdl, thp);

	return (ret);
}

/*
 * In post-processing we will link a maintenance-clear event to the
 * corresponding maintenance-entry event.  To achieve this we must
 * remember, for each service instance fmri, the uuid of the last
 * maintenance-entry event that we observed for that fmri.  We must persist
 * this cache so that we can link events separated by an intervening fmd
 * restart (or unload/load of this module).
 *
 * On reboot we want to throw this cache away, since maintenance states
 * that were in effect at shutdown will not see a corresponding clear event
 * during the reboot - even if the instance comes online this time.
 * We don't invalidate the cache at boot, but since any maintenance-entry
 * events in the new boot necessarily have different uuids to anything seen
 * before it doesn't matter - we'll just overwrite the old entry.
 *
 * The cache is restored or created on the first maintenance event we observe
 * since this module was loaded.  The initial number of cache entries is modest
 * at MID_CACHE_NENT_INC, and if ever we overflow we increment by that number
 * each time up to an absolute maximum of MID_CACHE_NENT_MAX.  When we supply
 * an event reference we invalidate that cache entry, allowing that cache
 * entry to be used for another maintenance event possibly for a different
 * service instance.  Thus MID_CACHE_NENT_MAX limits the number of
 * simultaneously-unresolved maintenance states that we can track.
 */

#define	MID_CACHE_NENT_INC		16
#define	MID_CACHE_NENT_MAX		128 /* integer multiple of the incr */

struct mid_cache_ent {
	char fmristr[90];
	char uuid[UUID_PRINTABLE_STRING_LENGTH];
	uint8_t flags;
};

#define	MID_CACHE_FL_VALID		0x1

#define	MID_CACHE_VERSION_1		0x11111111U
#define	MID_CACHE_VERSION		MID_CACHE_VERSION_1

struct mid_cache {
	uint32_t version;		/* Version */
	uint32_t nentries;		/* Real size of following array */
	struct mid_cache_ent entry[1];	/* Cache entries */
};

static struct mid_cache *mid_cache;

#define	MID_CACHE_BUFSZ(nent) (sizeof (struct mid_cache) + \
	((nent) - 1) * sizeof (struct mid_cache_ent))

#define	MUUID_CACHE_BUFNAME	"smf_maint_uuid_cache"

static void
mid_cache_persist(void)
{
	size_t sz = MID_CACHE_BUFSZ(mid_cache->nentries);

	fmd_buf_write(fmevt_hdl, NULL, MUUID_CACHE_BUFNAME, mid_cache, sz);
}

static void
mid_cache_unpersist(void)
{
	fmd_buf_destroy(fmevt_hdl, NULL, MUUID_CACHE_BUFNAME);
}

/*
 * Grow the cache if we're out of space; also used to establish initial cache.
 * Return a pointer to a free entry, or NULL if we've hit the maximum size.
 */
static struct mid_cache_ent *
mid_cache_grow(void)
{
	struct mid_cache *newcache;
	size_t newsz;
	uint32_t oldn, n;

	oldn = mid_cache == NULL ? 0 : mid_cache->nentries;
	if (oldn == MID_CACHE_NENT_MAX)
		return (NULL);

	n = oldn + MID_CACHE_NENT_INC;
	newsz = MID_CACHE_BUFSZ(n);
	newcache = fmd_hdl_zalloc(fmevt_hdl, newsz, FMD_SLEEP);
	newcache->version = MID_CACHE_VERSION;
	newcache->nentries = n;

	if (mid_cache != NULL) {
		size_t oldsz = MID_CACHE_BUFSZ(oldn);

		bcopy(&mid_cache->entry[0], &newcache->entry[0], oldsz);
		fmd_hdl_free(fmevt_hdl, mid_cache, oldsz);
		mid_cache_unpersist();
		mid_cache = NULL;
	}

	mid_cache = newcache;
	fmd_buf_create(fmevt_hdl, NULL, MUUID_CACHE_BUFNAME, newsz);
	mid_cache_persist();

	SETSTAT(smf_pp_cache_nents, n);
	fmevt_debug(FMEVT_DBG1, "mid_cache grown to %d entries", n);

	return (&mid_cache->entry[oldn]);
}

/*
 * Restore our uuid cache from checkpoint data.  Technically we want to
 * flush this checkpoint data at reboot, and restore it on subsequent
 * fmd restarts (if any), since instances in maintenance state at shutdown
 * will attempt to go online afresh at reboot time so we won't observe
 * a clear event.  Since any maintenance event in the new boot will
 * necessarily have a new uuid it doesn't matter - we'll just overwrite
 * the entry we restored from checkpoint.  We don't ever shrink the
 * number of cache entries
 */
static void
mid_cache_restore(boolean_t creat)
{
	struct mid_cache hdr;
	size_t sz;

	fmevt_debug(FMEVT_DBG1, "mid_cache_restore");

	sz = fmd_buf_size(fmevt_hdl, NULL, MUUID_CACHE_BUFNAME);

	/*
	 * If sz is zero then no such buffer exists and so this is the
	 * first time we're establishing the cache - just grow it from
	 * size 0, but only if we've been told we can create the cache.
	 */
	if (sz == 0) {
		fmevt_debug(FMEVT_DBG2, "mid_cache_restore - buf does not "
		    "exist");
		if (creat == B_TRUE)
			(void) mid_cache_grow();
		return;
	}

	fmd_buf_read(fmevt_hdl, NULL, MUUID_CACHE_BUFNAME, &hdr, sizeof (hdr));

	/*
	 * If the cache fails verification our policy is to bin it.
	 */
	if (hdr.version != MID_CACHE_VERSION) {
		BUMPSTAT(smf_pp_cache_badver);
		mid_cache_unpersist();
		if (creat == B_TRUE)
			(void) mid_cache_grow();
		return;
	} else if (sz != MID_CACHE_BUFSZ(hdr.nentries) ||
	    hdr.nentries > MID_CACHE_NENT_MAX) {
		BUMPSTAT(smf_pp_cache_badsz);
		mid_cache_unpersist();
		if (creat == B_TRUE)
			(void) mid_cache_grow();
		return;
	}

	mid_cache = fmd_hdl_alloc(fmevt_hdl, sz, FMD_SLEEP);
	fmd_buf_read(fmevt_hdl, NULL, MUUID_CACHE_BUFNAME, mid_cache, sz);
	fmevt_debug(FMEVT_DBG2, "cache restored with %d entries",
	    mid_cache->nentries);
}

/*
 * Add an entry to the maintenance UUID cache.
 */
static void
mid_cache_add(char *fmristr, const char *uuidstr)
{
	struct mid_cache_ent *entp = NULL;
	int i;

	fmevt_debug(FMEVT_DBG3, "Adding uuid %s for %s to cache",
	    uuidstr, fmristr);

	if (mid_cache == NULL)
		mid_cache_restore(B_TRUE);

	/*
	 * If we already have an entry for this fmri then overwrite it - we
	 * only ever need the last match.  If no match is found we remember
	 * the first invalid entry for use below.
	 */
	for (i = 0; i < mid_cache->nentries; i++) {
		if ((mid_cache->entry[i].flags & MID_CACHE_FL_VALID) == 0) {
			if (entp == NULL)
				entp = &mid_cache->entry[i];
			continue;
		}

		if (strcmp(fmristr, mid_cache->entry[i].fmristr) == 0) {
			(void) strncpy(&mid_cache->entry[i].uuid[0], uuidstr,
			    UUID_PRINTABLE_STRING_LENGTH);
			mid_cache_persist();
			BUMPSTAT(smf_pp_cache_adds);
			return;
		}
	}

	if (entp == NULL && (entp = mid_cache_grow()) == NULL) {
		BUMPSTAT(smf_pp_cache_drop);
		return;
	}

	(void) strncpy(entp->uuid, uuidstr, sizeof (entp->uuid));
	(void) strncpy(entp->fmristr, fmristr, sizeof (entp->fmristr));
	entp->flags |= MID_CACHE_FL_VALID;
	mid_cache_persist();

	BUMPSTAT(smf_pp_cache_adds);
}

/*
 * Lookup in the cache.  If we hit then immediately invalidate the
 * entry but return a pointer to the uuid string therein nonetheless -
 * the entry won't be purged until after the current module entry point
 * returns, at earliest.
 */
static char *
mid_cache_lookup(char *fmristr)
{
	int i;

	fmevt_debug(FMEVT_DBG3, "mid_cache_lookup of %s", fmristr);

	if (mid_cache == NULL) {
		mid_cache_restore(B_FALSE);
		if (mid_cache == NULL) {
			BUMPSTAT(smf_pp_cache_miss_empty);
			fmevt_debug(FMEVT_DBG3, "mid_cache is empty");
			return (NULL);
		}
	}

	for (i = 0; i < mid_cache->nentries; i++) {
		if (!(mid_cache->entry[i].flags & MID_CACHE_FL_VALID))
			continue;

		if (strcmp(fmristr, mid_cache->entry[i].fmristr) == 0) {
			mid_cache->entry[i].flags &= ~MID_CACHE_FL_VALID;
			mid_cache_persist();
			BUMPSTAT(smf_pp_cache_hit);
			fmevt_debug(FMEVT_DBG3, "cache hit: %s uuid %s",
			    fmristr, mid_cache->entry[i].uuid);
			return (&mid_cache->entry[i].uuid[0]);
		}
	}

	BUMPSTAT(smf_pp_cache_miss);
	return (NULL);
}

/*ARGSUSED*/
uint_t
fmevt_pp_smf(char *classes[FMEVT_FANOUT_MAX],
    nvlist_t *attr[FMEVT_FANOUT_MAX], const char *ruleset,
    const nvlist_t *detector, nvlist_t *rawattr,
    const struct fmevt_ppargs *eap)
{
	int32_t transition, from, to;
	const char *fromstr, *tostr;
	char *svcname, *rsn, *rsnl;
	char *linkuuid = NULL;
	nvlist_t *myattr;
	nvlist_t *fmri;
	uint32_t ver;

	if (!fmd_prop_get_int32(fmevt_hdl, "inbound_postprocess_smf"))
		return (0);

	if (rawattr == NULL ||
	    strcmp(eap->pp_rawclass, "state-transition") != 0 ||
	    nvlist_lookup_string(rawattr, "fmri", &svcname) != 0 ||
	    nvlist_lookup_int32(rawattr, "transition", &transition) != 0 ||
	    nvlist_lookup_string(rawattr, "reason-short", &rsn) != 0 ||
	    nvlist_lookup_string(rawattr, "reason-long", &rsnl) != 0 ||
	    nvlist_lookup_uint32(rawattr, "reason-version", &ver) != 0)
		return (0);

	from = transition >> 16;
	to = transition & 0xffff;

	fromstr = smf_state_to_string(from);
	tostr = smf_state_to_string(to);

	if (fromstr == NULL || tostr == NULL)
		return (0);

	if (strcmp(eap->pp_rawsubclass, tostr) != 0)
		return (0);

	if ((fmri = shortfmri_to_fmri(svcname)) == NULL)
		return (0);

	if (snprintf(classes[0], FMEVT_MAX_CLASS, "%s.%s.%s.%s",
	    FM_IREPORT_CLASS, "os.smf", eap->pp_rawclass,
	    eap->pp_rawsubclass) >= FMEVT_MAX_CLASS - 1)
		return (0);

	if ((myattr = fmd_nvl_alloc(fmevt_hdl, FMD_SLEEP)) == NULL)
		return (0);

	if (to == SCF_STATE_MAINT)
		mid_cache_add(svcname, eap->pp_uuidstr);
	else if (from == SCF_STATE_MAINT)
		linkuuid = mid_cache_lookup(svcname);

	if (nvlist_add_nvlist(myattr, "svc", fmri) != 0 ||
	    nvlist_add_string(myattr, "svc-string", svcname) != 0 ||
	    nvlist_add_string(myattr, "from-state", fromstr) != 0 ||
	    nvlist_add_string(myattr, "to-state", tostr) != 0 ||
	    nvlist_add_uint32(myattr, "reason-version", ver) != 0 ||
	    nvlist_add_string(myattr, "reason-short", rsn) != 0 ||
	    nvlist_add_string(myattr, "reason-long", rsnl) != 0 || linkuuid &&
	    nvlist_add_string(myattr, "linked-event", linkuuid) != 0) {
		nvlist_free(fmri);
		nvlist_free(myattr);
		return (0);
	}

	attr[0] = myattr;
	nvlist_free(fmri);

	return (1);	/* one event to publish */
}

void
fmevt_pp_smf_init(fmd_hdl_t *hdl)
{
	(void) fmd_stat_create(hdl, FMD_STAT_NOALLOC,
	    sizeof (fmevt_pp_smf_stats) / sizeof (fmd_stat_t),
	    (fmd_stat_t *)&fmevt_pp_smf_stats);
}

/*ARGSUSED*/
void
fmevt_pp_smf_fini(fmd_hdl_t *hdl)
{
	if (mid_cache) {
		fmd_hdl_free(fmevt_hdl, mid_cache,
		    MID_CACHE_BUFSZ(mid_cache->nentries));
		mid_cache = NULL;
	}
}
